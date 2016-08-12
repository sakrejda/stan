#ifndef STAN_SERVICES_OPTIMIZE_LBFGS_HPP
#define STAN_SERVICES_OPTIMIZE_LBFGS_HPP

#include <stan/io/var_context.hpp>
#include <stan/io/chained_var_context.hpp>
#include <stan/io/random_var_context.hpp>
#include <stan/callbacks/writer.hpp>
#include <stan/model/util.hpp>
#include <stan/optimization/bfgs.hpp>
#include <stan/services/error_codes.hpp>
#include <stan/services/util/initialize.hpp>
#include <stan/services/util/rng.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>


namespace stan {
  namespace services {
    namespace optimize {

      /**
       * Runs the L-BFGS algorithm for a model.
       *
       * @tparam Model A model implementation
       * @tparam Interrupt type of interrupt
       *
       * @param model Input model to test (with data already instantiated)
       * @param init var context for initialization
       * @param random_seed random seed for the pseudo random number generator
       * @param chain chain id to advance the pseudo random number generator
       * @param init_radius radius to initialize
       * @param[in] history_size
       * @param[in] init_alpha
       * @param[in] tol_obj
       * @param[in] tol_rel_obj
       * @param[in] tol_grad
       * @param[in] tol_rel_grad
       * @param[in] tol_param
       * @param[in] num_iterations maximum number of iterations
       * @param[in] save_iterations indicates whether all the interations should
       *   be saved to the parameter_writer
       * @param[in] refresh how often to write output to message_writer
       * @param[out] interrupt interrupt callback to be called every iteration
       * @param[out] message_writer output for messages
       * @param[out] init_writer Writer callback for unconstrained inits
       * @param[out] parameter_writer output for parameter values
       * @return stan::services::error_codes::OK (0) if successful
       */
      template <class Model, typename Interrupt>
      int lbfgs(Model& model,
                stan::io::var_context& init,
                unsigned int random_seed,
                unsigned int chain,
                double init_radius,
                int history_size,
                double init_alpha,
                double tol_obj,
                double tol_rel_obj,
                double tol_grad,
                double tol_rel_grad,
                double tol_param,
                int num_iterations,
                bool save_iterations,
                int refresh,
                Interrupt& interrupt,
                callbacks::writer& message_writer,
                callbacks::writer& init_writer,
                callbacks::writer& parameter_writer) {
        boost::ecuyer1988 rng = stan::services::util::rng(random_seed, chain);

        std::vector<int> disc_vector;
        std::vector<double> cont_vector
          = stan::services::util::initialize(model, init, rng, init_radius,
                                             false,
                                             message_writer, init_writer);

        std::stringstream lbfgs_ss;
        typedef stan::optimization::BFGSLineSearch
          <Model, stan::optimization::LBFGSUpdate<> > Optimizer;
        Optimizer lbfgs(model, cont_vector, disc_vector, &lbfgs_ss);
        lbfgs.get_qnupdate().set_history_size(history_size);
        lbfgs._ls_opts.alpha0 = init_alpha;
        lbfgs._conv_opts.tolAbsF = tol_obj;
        lbfgs._conv_opts.tolRelF = tol_rel_obj;
        lbfgs._conv_opts.tolAbsGrad = tol_grad;
        lbfgs._conv_opts.tolRelGrad = tol_rel_grad;
        lbfgs._conv_opts.tolAbsX = tol_param;
        lbfgs._conv_opts.maxIts = num_iterations;

        double lp = lbfgs.logp();

        std::stringstream msg;
        msg << "Initial log joint probability = " << lp;
        message_writer(msg.str());

        std::vector<std::string> names;
        names.push_back("lp__");
        model.constrained_param_names(names, true, true);
        parameter_writer(names);

        if (save_iterations) {
          std::vector<double> values;
          msg.str("");
          model.write_array(rng, cont_vector, disc_vector, values,
                            true, true, &msg);
          if (msg.str().length() > 0)
            message_writer(msg.str());

          values.insert(values.begin(), lp);
          parameter_writer(values);
        }
        int ret = 0;

        while (ret == 0) {
          interrupt();
          if (refresh > 0
              && (lbfgs.iter_num() == 0
                  || (lbfgs.iter_num() + 1 % refresh == 0)))
            message_writer("    Iter "
                           "     log prob "
                           "       ||dx|| "
                           "     ||grad|| "
                           "      alpha "
                           "     alpha0 "
                           " # evals "
                           " Notes ");

          ret = lbfgs.step();
          lp = lbfgs.logp();
          lbfgs.params_r(cont_vector);

          if (refresh > 0
              && (ret != 0
                  || !lbfgs.note().empty()
                  || lbfgs.iter_num() == 0
                  || (lbfgs.iter_num() + 1 % refresh == 0))) {
            msg.str("");
            msg << " " << std::setw(7) << lbfgs.iter_num() << " ";
            msg << " " << std::setw(12) << std::setprecision(6)
                << lp << " ";
            msg << " " << std::setw(12) << std::setprecision(6)
                << lbfgs.prev_step_size() << " ";
            msg << " " << std::setw(12) << std::setprecision(6)
                << lbfgs.curr_g().norm() << " ";
            msg << " " << std::setw(10) << std::setprecision(4)
                << lbfgs.alpha() << " ";
            msg << " " << std::setw(10) << std::setprecision(4)
                << lbfgs.alpha0() << " ";
            msg << " " << std::setw(7)
                << lbfgs.grad_evals() << " ";
            msg << " " << lbfgs.note() << " ";
            message_writer(msg.str());
          }

          if (lbfgs_ss.str().length() > 0) {
            message_writer(lbfgs_ss.str());
            lbfgs_ss.str("");
          }

          if (save_iterations) {
            std::vector<double> values;
            msg.str("");
            model.write_array(rng, cont_vector, disc_vector, values,
                              true, true, &msg);
            if (msg.str().length() > 0)
              message_writer(msg.str());

            values.insert(values.begin(), lp);
            parameter_writer(values);
          }
        }

        if (!save_iterations) {
          std::vector<double> values;
          msg.str("");
          model.write_array(rng, cont_vector, disc_vector, values,
                            true, true, &msg);
          if (msg.str().length() > 0)
            message_writer(msg.str());

          values.insert(values.begin(), lp);
          parameter_writer(values);
        }

        int return_code;
        if (ret >= 0) {
          message_writer("Optimization terminated normally: ");
          return_code = stan::services::error_codes::OK;
        } else {
          message_writer("Optimization terminated with error: ");
          return_code = stan::services::error_codes::SOFTWARE;
        }
        message_writer("  " + lbfgs.get_code_string(ret));

        return return_code;
      }

    }
  }
}
#endif